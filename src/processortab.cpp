#include "processortab.h"
#include "ui_processortab.h"

#include <QDir>
#include <QDialog>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryFile>
#include <QTableWidget>
#include <QVBoxLayout>

#include <limits>
#include <vector>

#include "consolewidget.h"
#include "fpuniciclediagramwidget.h"
#include "instructionmodel.h"
#include "pipelinediagrammodel.h"
#include "pipelinediagramwidget.h"
#include "processorhandler.h"
#include "processorregistry.h"
#include "processorselectiondialog.h"
#include "cacheselectiondialog.h"
#include "registercontainerwidget.h"
#include "registermodel.h"
#include "ripessettings.h"
#include "cachesim/cachetypes.h"
#include "syscall/systemio.h"

#include "VSRTL/graphics/vsrtl_widget.h"
#include "VSRTL/core/vsrtl_design.h"

#include "processors/interface/ripesprocessor.h"

namespace Ripes {

class StallHistoryChart : public QWidget {
public:
  explicit StallHistoryChart(QWidget *parent = nullptr) : QWidget(parent) {
    setMinimumHeight(230);
  }

  void setHistories(std::vector<uint64_t> data,
                    std::vector<uint64_t> structural,
                    std::vector<uint64_t> control) {
    m_data = std::move(data);
    m_structural = std::move(structural);
    m_control = std::move(control);
    update();
  }

  void setSelectedCycle(uint64_t cycle) {
    m_selectedCycle = cycle;
    setMinimumHeight(cycle == 0 ? 230 : 330);
    updateGeometry();
    update();
  }

  void setCycleRange(uint64_t first, uint64_t last) {
    m_rangeFirst = first;
    m_rangeLast = last;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());

    const qreal bottomMargin = m_selectedCycle == 0 ? 45 : 135;
    const QRectF plot =
        QRectF(rect()).adjusted(75, 24, -20, -bottomMargin);
    painter.setPen(palette().text().color());
    painter.drawLine(plot.bottomLeft(), plot.topLeft());
    painter.drawLine(plot.bottomLeft(), plot.bottomRight());
    painter.save();
    painter.translate(16, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-plot.height() / 2, -10, plot.height(), 20),
                     Qt::AlignCenter, "Accumulated stalls");
    painter.restore();
    painter.drawText(QRectF(plot.left(), plot.bottom() + 8, plot.width(), 25),
                     Qt::AlignCenter, "Cycle");

    const std::size_t fullSampleCount =
        std::max({m_data.size(), m_structural.size(), m_control.size()});
    const uint64_t firstCycle =
        m_rangeFirst == 0 ? 1 : std::min<uint64_t>(m_rangeFirst, fullSampleCount);
    const uint64_t lastCycle =
        m_rangeLast == 0 ? fullSampleCount
                         : std::min<uint64_t>(m_rangeLast, fullSampleCount);
    const auto visibleData = rangeValues(m_data, firstCycle, lastCycle);
    const auto visibleStructural =
        rangeValues(m_structural, firstCycle, lastCycle);
    const auto visibleControl = rangeValues(m_control, firstCycle, lastCycle);
    const std::size_t sampleCount = visibleData.size();
    const uint64_t maximum = std::max<uint64_t>(
        1, std::max({lastValue(visibleData), lastValue(visibleStructural),
                     lastValue(visibleControl)}));
    painter.drawText(QRectF(0, plot.top() - 8, 48, 20), Qt::AlignRight,
                     QString::number(maximum));
    painter.drawText(QRectF(0, plot.bottom() - 10, 48, 20), Qt::AlignRight,
                     QString::number(m_rangeFirst == 0 ? 0 : firstCycle));
    painter.drawText(QRectF(plot.left() - 10, plot.bottom() + 4, 40, 20),
                     Qt::AlignLeft, "0");
    painter.drawText(QRectF(plot.right() - 60, plot.bottom() + 4, 60, 20),
                     Qt::AlignRight,
                     QString::number(lastCycle));

    drawSeries(painter, plot, visibleControl, maximum, QColor(210, 45, 45));
    drawSeries(painter, plot, visibleStructural, maximum, QColor(45, 105, 210));
    drawSeries(painter, plot, visibleData, maximum, QColor(225, 180, 20));

    const QColor textColor = palette().text().color();
    if (m_selectedCycle >= firstCycle && m_selectedCycle <= lastCycle &&
        sampleCount > 0) {
      const qreal selectedX =
          plot.left() +
          plot.width() *
              static_cast<qreal>(m_selectedCycle - firstCycle + 1) /
              static_cast<qreal>(sampleCount);
      painter.setPen(QPen(textColor, 1.5, Qt::DashLine));
      painter.drawLine(QPointF(selectedX, plot.top()),
                       QPointF(selectedX, plot.bottom()));
      painter.drawText(QRectF(selectedX - 45, plot.top(), 90, 20),
                       Qt::AlignCenter,
                       QString("Cycle %1").arg(m_selectedCycle));

      const QRectF heatmap(plot.left(), plot.bottom() + 48, plot.width(), 66);
      painter.setPen(textColor);
      painter.drawText(QRectF(heatmap.left(), heatmap.top() - 23,
                              heatmap.width(), 20),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString("Stalls at cycle %1").arg(m_selectedCycle));
      drawSelectedCycleRow(
          painter, heatmap, 0, m_data, QColor(225, 180, 20), textColor,
          palette().mid().color(), palette().alternateBase().color(), "Data");
      drawSelectedCycleRow(
          painter, heatmap, 1, m_structural, QColor(45, 105, 210), textColor,
          palette().mid().color(), palette().alternateBase().color(),
          "Structural");
      drawSelectedCycleRow(
          painter, heatmap, 2, m_control, QColor(210, 45, 45), textColor,
          palette().mid().color(), palette().alternateBase().color(),
          "Control");
    }

    drawLegend(painter, plot.left(), 5, QColor(225, 180, 20), textColor,
               "Data");
    drawLegend(painter, plot.left() + 90, 5, QColor(45, 105, 210),
               textColor, "Structural");
    drawLegend(painter, plot.left() + 205, 5, QColor(210, 45, 45),
               textColor, "Control");
  }

private:
  static uint64_t lastValue(const std::vector<uint64_t> &values) {
    return values.empty() ? 0 : values.back();
  }

  static void drawSeries(QPainter &painter, const QRectF &plot,
                         const std::vector<uint64_t> &values,
                         uint64_t maximum, const QColor &color) {
    if (values.empty())
      return;
    QPainterPath path;
    qreal previousY = plot.bottom();
    path.moveTo(plot.left(), previousY);
    for (std::size_t i = 0; i < values.size(); ++i) {
      const qreal x = plot.left() +
                      plot.width() * static_cast<qreal>(i + 1) /
                          static_cast<qreal>(values.size());
      const qreal y = plot.bottom() -
                      plot.height() * static_cast<qreal>(values[i]) /
                          static_cast<qreal>(maximum);
      path.lineTo(x, previousY);
      path.lineTo(x, y);
      previousY = y;
    }
    painter.setPen(QPen(color, 2.5));
    painter.drawPath(path);
  }

  static void drawLegend(QPainter &painter, qreal x, qreal y,
                         const QColor &color, const QColor &textColor,
                         const QString &text) {
    painter.fillRect(QRectF(x, y + 4, 14, 4), color);
    painter.setPen(textColor);
    painter.drawText(QRectF(x + 20, y - 3, 90, 20), text);
  }

  static std::vector<uint64_t>
  rangeValues(const std::vector<uint64_t> &values, uint64_t first,
              uint64_t last) {
    std::vector<uint64_t> result;
    if (values.empty() || first == 0 || first > last || first > values.size())
      return result;
    last = std::min<uint64_t>(last, values.size());
    const uint64_t baseline = first <= 1 ? 0 : values[first - 2];
    result.reserve(static_cast<std::size_t>(last - first + 1));
    for (uint64_t cycle = first; cycle <= last; ++cycle)
      result.push_back(values[cycle - 1] - baseline);
    return result;
  }

  void drawSelectedCycleRow(
      QPainter &painter, const QRectF &heatmap, unsigned row,
      const std::vector<uint64_t> &history, const QColor &color,
      const QColor &textColor, const QColor &borderColor,
      const QColor &backgroundColor, const QString &label) {
    constexpr qreal rowHeight = 18;
    constexpr qreal rowGap = 4;
    const qreal y = heatmap.top() + row * (rowHeight + rowGap);
    const QRectF band(heatmap.left(), y, heatmap.width(), rowHeight);
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(backgroundColor);
    painter.drawRect(band);
    painter.setPen(textColor);
    painter.drawText(QRectF(0, y, heatmap.left() - 8, rowHeight),
                     Qt::AlignRight | Qt::AlignVCenter, label);

    const std::size_t index = static_cast<std::size_t>(m_selectedCycle - 1);
    bool stalled = false;
    if (index < history.size()) {
      const uint64_t previous = index == 0 ? 0 : history[index - 1];
      if (history[index] > previous) {
        stalled = true;
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRect(band);
      }
    }
    painter.setPen(textColor);
    painter.drawText(band, Qt::AlignCenter, stalled ? "Stall" : "None");
  }

  std::vector<uint64_t> m_data;
  std::vector<uint64_t> m_structural;
  std::vector<uint64_t> m_control;
  uint64_t m_selectedCycle = 0;
  uint64_t m_rangeFirst = 0;
  uint64_t m_rangeLast = 0;
};

static QString convertToSIUnits(const double l_value, int precision = 2) {
  QString unit;
  double value;

  if (l_value < 0) {
    value = l_value * -1;
  } else {
    value = l_value;
  }

  if (value >= 1000000 && value < 1000000000) {
    value = value / 1000000;
    unit = "M";
  } else if (value >= 1000 && value < 1000000) {
    value = value / 1000;
    unit = "K";
  } else if (value >= 1 && value < 1000) {
    value = value * 1;
  } else if ((value * 1000) >= 1 && value < 1000) {
    value = value * 1000;
    unit = "m";
  } else if ((value * 1000000) >= 1 && value < 1000000) {
    value = value * 1000000;
    unit = QChar(0x00B5);
  } else if ((value * 1000000000) >= 1 && value < 1000000000) {
    value = value * 1000000000;
    unit = "n";
  }

  if (l_value > 0) {
    return (QString::number(value, 10, precision) + " " + unit);
  } else if (l_value < 0) {
    return (QString::number(value * -1, 10, precision) + " " + unit);
  }
  return QString::number(0) + " ";
}

ProcessorTab::ProcessorTab(QToolBar *controlToolbar,
                           QToolBar *additionalToolbar, QWidget *parent)
    : RipesTab(additionalToolbar, parent), m_ui(new Ui::ProcessorTab) {
  m_ui->setupUi(this);

  m_vsrtlWidget = m_ui->vsrtlWidget;

  if (ProcessorHandler::isVSRTLProcessor()) {
    // Load the default constructed processor to the VSRTL widget. Do a bit of
    // sanity checking to ensure that the layout stored in the settings is valid
    // for the given processor
    unsigned layoutID =
        RipesSettings::value(RIPES_SETTING_PROCESSOR_LAYOUT_ID).toInt();
    const Layout *layout = nullptr;
    if (layoutID >= ProcessorRegistry::getDescription(ProcessorHandler::getID())
                        .layouts.size()) {
      layoutID = 0;
    }
    const auto &layouts =
        ProcessorRegistry::getDescription(ProcessorHandler::getID()).layouts;
    if (layouts.size() > layoutID) {
      layout = &layouts.at(layoutID);
    }
    loadProcessorToWidget(layout);

    // By default, lock the VSRTL widget
    m_vsrtlWidget->setLocked(true);
  }

  m_stageModel = new PipelineDiagramModel(this);
  m_fpUnicicleDiagramWidget = new FPUnicicleDiagramWidget(this);
  m_advancedStatisticsDialog = new QDialog(this);
  m_advancedStatisticsDialog->setWindowFlag(Qt::Window, true);
  m_advancedStatisticsDialog->setWindowTitle(
      "Advanced execution statistics");
  m_advancedStatisticsDialog->resize(800, 760);
  auto *statisticsLayout = new QVBoxLayout(m_advancedStatisticsDialog);
  statisticsLayout->setContentsMargins(24, 20, 24, 20);
  auto *statisticsTitle = new QLabel("Advanced execution statistics");
  QFont statisticsTitleFont = statisticsTitle->font();
  statisticsTitleFont.setPointSize(statisticsTitleFont.pointSize() + 4);
  statisticsTitleFont.setBold(true);
  statisticsTitle->setFont(statisticsTitleFont);
  statisticsLayout->addWidget(statisticsTitle);
  m_advancedStatisticsSummary = new QLabel;
  m_advancedStatisticsSummary->setWordWrap(true);
  statisticsLayout->addWidget(m_advancedStatisticsSummary);

  m_advancedStatisticsTable = new QTableWidget(10, 3);
  m_advancedStatisticsTable->setHorizontalHeaderLabels(
      {"Resource / event", "Cycles", "%"});
  m_advancedStatisticsTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  m_advancedStatisticsTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  m_advancedStatisticsTable->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  m_advancedStatisticsTable->verticalHeader()->setVisible(false);
  m_advancedStatisticsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_advancedStatisticsTable->setSelectionMode(
      QAbstractItemView::NoSelection);
  statisticsLayout->addWidget(m_advancedStatisticsTable);

  m_stallHistoryChart = new StallHistoryChart;
  auto *rangeLayout = new QHBoxLayout;
  rangeLayout->addWidget(new QLabel("Show stats between cycle"));
  m_statisticsRangeStart = new QSpinBox;
  m_statisticsRangeEnd = new QSpinBox;
  m_statisticsRangeStart->setRange(1, 1);
  m_statisticsRangeEnd->setRange(1, 1);
  rangeLayout->addWidget(m_statisticsRangeStart);
  rangeLayout->addWidget(new QLabel("and cycle"));
  rangeLayout->addWidget(m_statisticsRangeEnd);
  auto *showRangeButton = new QPushButton("Show");
  auto *clearRangeButton = new QPushButton("Clear");
  rangeLayout->addWidget(showRangeButton);
  rangeLayout->addWidget(clearRangeButton);
  rangeLayout->addStretch();
  statisticsLayout->addLayout(rangeLayout);

  statisticsLayout->addWidget(m_stallHistoryChart);

  auto *cycleSearchLayout = new QHBoxLayout;
  cycleSearchLayout->addWidget(new QLabel("Inspect stall cycle:"));
  m_stallCycleSearch = new QSpinBox;
  m_stallCycleSearch->setRange(0, 0);
  m_stallCycleSearch->setSpecialValueText(" ");
  m_stallCycleSearch->setToolTip("Cycle to inspect");
  cycleSearchLayout->addWidget(m_stallCycleSearch);
  auto *searchCycleButton = new QPushButton("Search");
  auto *clearCycleButton = new QPushButton("Clear");
  cycleSearchLayout->addWidget(searchCycleButton);
  cycleSearchLayout->addWidget(clearCycleButton);
  cycleSearchLayout->addStretch();
  statisticsLayout->addLayout(cycleSearchLayout);
  connect(searchCycleButton, &QPushButton::clicked, this, [this] {
    m_stallHistoryChart->setSelectedCycle(
        static_cast<uint64_t>(m_stallCycleSearch->value()));
  });
  connect(clearCycleButton, &QPushButton::clicked, this, [this] {
    m_stallCycleSearch->setValue(0);
    m_stallHistoryChart->setSelectedCycle(0);
  });

  connect(showRangeButton, &QPushButton::clicked, this, [this] {
    const uint64_t first = static_cast<uint64_t>(
        std::min(m_statisticsRangeStart->value(),
                 m_statisticsRangeEnd->value()));
    const uint64_t last = static_cast<uint64_t>(
        std::max(m_statisticsRangeStart->value(),
                 m_statisticsRangeEnd->value()));
    m_statisticsRangeEnabled = true;
    m_stallHistoryChart->setCycleRange(first, last);
    updateAdvancedStatistics();
  });
  connect(clearRangeButton, &QPushButton::clicked, this, [this] {
    m_statisticsRangeEnabled = false;
    m_stallHistoryChart->setCycleRange(0, 0);
    updateAdvancedStatistics();
  });

  updateInstructionModel();
  connect(ProcessorHandler::get(), &ProcessorHandler::procStateChangedNonRun,
          this, &ProcessorTab::updateStatistics);
  connect(ProcessorHandler::get(), &ProcessorHandler::procStateChangedNonRun,
          this, &ProcessorTab::updateInstructionLabels);
  connect(ProcessorHandler::get(), &ProcessorHandler::procStateChangedNonRun,
          this, [this] {
            m_reverseAction->setEnabled(m_vsrtlWidget->isReversible() &&
                                        !m_autoClockAction->isChecked());
            if (m_targetCycle && !m_targetCycle->hasFocus()) {
              const auto cycle =
                  ProcessorHandler::getProcessor()->getCycleCount();
              m_targetCycle->setValue(static_cast<int>(std::min<long long>(
                  cycle, std::numeric_limits<int>::max())));
            }
          });

  setupSimulatorActions(controlToolbar);

  // Setup statistics update timer - this timer is distinct from the
  // ProcessorHandler's update timer, given that it needs to run during
  // 'running' the processor.
  m_statUpdateTimer = new QTimer(this);
  m_statUpdateTimer->setInterval(
      1000.0 / RipesSettings::value(RIPES_SETTING_UIUPDATEPS).toInt());
  connect(m_statUpdateTimer, &QTimer::timeout, this,
          &ProcessorTab::updateStatistics);
  connect(RipesSettings::getObserver(RIPES_SETTING_UIUPDATEPS),
          &SettingObserver::modified, m_statUpdateTimer, [this] {
            m_statUpdateTimer->setInterval(
                1000.0 /
                RipesSettings::value(RIPES_SETTING_UIUPDATEPS).toInt());
          });

  // Connect changes in VSRTL reversible stack size to checking whether the
  // simulator is reversible
  connect(RipesSettings::getObserver(RIPES_SETTING_REWINDSTACKSIZE),
          &SettingObserver::modified, m_reverseAction, [this](const auto &) {
            m_reverseAction->setEnabled(m_vsrtlWidget->isReversible());
          });

  // Connect the global reset request signal to reset()
  connect(ProcessorHandler::get(), &ProcessorHandler::processorReset, this,
          &ProcessorTab::reset);
  connect(ProcessorHandler::get(), &ProcessorHandler::exit, this,
          &ProcessorTab::processorFinished);
  connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, this,
          &ProcessorTab::runFinished);
  connect(ProcessorHandler::get(), &ProcessorHandler::stopping, this,
          &ProcessorTab::pause);

  // Make processor view stretch wrt. consoles
  m_ui->pipelinesplitter->setStretchFactor(0, 1);
  m_ui->pipelinesplitter->setStretchFactor(1, 0);

  // Make processor view stretch wrt. right side tabs
  m_ui->viewSplitter->setStretchFactor(0, 1);
  m_ui->viewSplitter->setStretchFactor(1, 0);

  // Adjust sizing between register view and instruction view
  m_ui->rightBarSplitter->setStretchFactor(0, 6);
  m_ui->rightBarSplitter->setStretchFactor(1, 1);

  // Initially, no file is loaded, disable toolbuttons
  enableSimulatorControls();
}

void ProcessorTab::loadLayout(const Layout &layout) {
  if (layout.name.isEmpty() || layout.file.isEmpty())
    return; // Not a valid layout

  if (layout.stageLabelPositions.size() !=
      ProcessorHandler::getProcessor()->structure().numStages()) {
    Q_ASSERT(false &&
             "A stage label position must be specified for each stage");
  }

  // cereal expects the archive file to be present standalone on disk, and
  // available through an ifstream. Copy the resource layout file (bundled
  // within the binary as a Qt resource) to a temporary file, for loading the
  // layout.
  const auto &layoutResourceFilename = layout.file;
  QFile layoutResourceFile(layoutResourceFilename);
  QTemporaryFile *tmpLayoutFile =
      QTemporaryFile::createNativeFile(layoutResourceFile);
  if (!tmpLayoutFile->open()) {
    QMessageBox::warning(this, "Error",
                         "Could not create temporary layout file");
    return;
  }

  m_vsrtlWidget->getTopLevelComponent()->loadLayoutFile(
      tmpLayoutFile->fileName());
  tmpLayoutFile->remove();

  // Adjust stage label positions
  const auto &parent = m_stageInstructionLabels.at({0, 0})->parentItem();
  for (auto sid : ProcessorHandler::getProcessor()->structure().stageIt()) {
    auto &label = m_stageInstructionLabels.at(sid);
    QFontMetrics metrics(label->font());
    label->setPos(parent->boundingRect().width() *
                      layout.stageLabelPositions.at(sid).x(),
                  metrics.height() * layout.stageLabelPositions.at(sid).y());
  }
}

void ProcessorTab::setupSimulatorActions(QToolBar *controlToolbar) {
  const QIcon processorIcon = QIcon(":/icons/cpu.svg");
  m_selectProcessorAction =
      new QAction(processorIcon, "Select processor", this);
  connect(m_selectProcessorAction, &QAction::triggered, this,
          &ProcessorTab::processorSelection);
  controlToolbar->addAction(m_selectProcessorAction);
 
  const QIcon cacheIcon = QIcon(":/icons/hierarchy_test.svg");
  m_selectCacheAction =
      new QAction(cacheIcon, "Select cache", this);
  connect(m_selectCacheAction, &QAction::triggered, this,
          &ProcessorTab::cacheSelection);
  controlToolbar->addAction(m_selectCacheAction);
  controlToolbar->addSeparator();

  const QIcon resetIcon = QIcon(":/icons/reset.svg");
  m_resetAction = new QAction(resetIcon, "Reset (F3)", this);
  connect(m_resetAction, &QAction::triggered, this, [this] {
    RipesSettings::getObserver(RIPES_GLOBALSIGNAL_REQRESET)->trigger();
  });
  m_resetAction->setShortcut(QKeySequence("F3"));
  m_resetAction->setToolTip("Reset the simulator (F3)");
  controlToolbar->addAction(m_resetAction);

  const QIcon reverseIcon = QIcon(":/icons/reverse.svg");
  m_reverseAction = new QAction(reverseIcon, "Reverse (F4)", this);
  connect(m_reverseAction, &QAction::triggered, this, &ProcessorTab::reverse);
  m_reverseAction->setShortcut(QKeySequence("F4"));
  m_reverseAction->setToolTip("Undo a clock cycle (F4)");
  controlToolbar->addAction(m_reverseAction);

  const QIcon clockIcon = QIcon(":/icons/step.svg");
  m_clockAction = new QAction(clockIcon, "Clock (F5)", this);
  connect(m_clockAction, &QAction::triggered, this,
          [] { ProcessorHandler::clock(); });
  m_clockAction->setShortcut(QKeySequence("F5"));
  m_clockAction->setToolTip("Clock the circuit (F5)");
  controlToolbar->addAction(m_clockAction);

  m_autoClockTimer = new QTimer(this);
  connect(m_autoClockTimer, &QTimer::timeout, this,
          [this] { autoClockTimeout(); });

  const QIcon startAutoClockIcon = QIcon(":/icons/step-clock.svg");
  m_autoClockAction = new QAction(startAutoClockIcon, "Auto clock (F6)", this);
  m_autoClockAction->setShortcut(QKeySequence("F6"));
  m_autoClockAction->setToolTip(
      "Clock the circuit with the selected frequency (F6)");
  m_autoClockAction->setCheckable(true);
  m_autoClockAction->setChecked(false);
  connect(m_autoClockAction, &QAction::toggled, this, &ProcessorTab::autoClock);
  controlToolbar->addAction(m_autoClockAction);

  m_autoClockInterval = new QSpinBox(this);
  m_autoClockInterval->setRange(1, 10000);
  m_autoClockInterval->setSuffix(" ms");
  m_autoClockInterval->setToolTip("Auto clock interval");
  connect(m_autoClockInterval, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int msec) {
            RipesSettings::setValue(RIPES_SETTING_AUTOCLOCK_INTERVAL, msec);
            m_autoClockTimer->setInterval(msec);
          });
  m_autoClockInterval->setValue(
      RipesSettings::value(RIPES_SETTING_AUTOCLOCK_INTERVAL).toInt());
  controlToolbar->addWidget(m_autoClockInterval);

  const QIcon runIcon = QIcon(":/icons/run.svg");
  m_runAction = new QAction(runIcon, "Run (F8)", this);
  m_runAction->setShortcut(QKeySequence("F8"));
  m_runAction->setCheckable(true);
  m_runAction->setChecked(false);
  m_runAction->setToolTip(
      "Execute simulator without updating UI (fast execution) (F8).\n Running "
      "will stop once the program exits or a "
      "breakpoint is hit.");
  connect(m_runAction, &QAction::toggled, this, &ProcessorTab::run);
  controlToolbar->addAction(m_runAction);

  m_targetCycle = new QSpinBox(this);
  m_targetCycle->setRange(0, std::numeric_limits<int>::max());
  m_targetCycle->setPrefix("cycle ");
  m_targetCycle->setToolTip("Target cycle");
  if (const auto *processor = ProcessorHandler::getProcessor()) {
    m_targetCycle->setValue(static_cast<int>(std::min<long long>(
        processor->getCycleCount(), std::numeric_limits<int>::max())));
  }
  controlToolbar->addWidget(m_targetCycle);

  const QIcon goToCycleIcon(":/icons/go-to-cycle.svg");
  m_goToCycleAction = new QAction(goToCycleIcon, "Go to cycle", this);
  m_goToCycleAction->setToolTip(
      "Run or rewind the simulator to the selected cycle");
  connect(m_goToCycleAction, &QAction::triggered, this,
          &ProcessorTab::goToCycle);
  controlToolbar->addAction(m_goToCycleAction);

  // Setup processor-tab only actions
  m_displayValuesAction = new QAction("Show processor signal values", this);
  m_displayValuesAction->setCheckable(true);
  connect(m_displayValuesAction, &QAction::toggled, m_vsrtlWidget,
          [this](bool checked) {
            RipesSettings::setValue(RIPES_SETTING_SHOWSIGNALS,
                                    QVariant::fromValue(checked));
            m_vsrtlWidget->setOutputPortValuesVisible(checked);
          });
  m_displayValuesAction->setChecked(
      RipesSettings::value(RIPES_SETTING_SHOWSIGNALS).toBool());

  const QIcon tableIcon = QIcon(":/icons/spreadsheet.svg");
  m_pipelineDiagramAction =
      new QAction(tableIcon, "Show pipeline diagram", this);
  connect(m_pipelineDiagramAction, &QAction::triggered, this,
          &ProcessorTab::showPipelineDiagram);
  m_toolbar->addAction(m_pipelineDiagramAction);

  const QIcon fpUnicicleIcon(":/icons/unicycle-diagram.svg");
  m_fpUnicicleDiagramAction =
      new QAction(fpUnicicleIcon, "Show FP unicicle diagram", this);
  connect(m_fpUnicicleDiagramAction, &QAction::triggered, this,
          &ProcessorTab::showFPUnicicleDiagram);
  m_toolbar->addAction(m_fpUnicicleDiagramAction);

  const QIcon advancedStatisticsIcon(":/icons/advanced-statistics.svg");
  m_advancedStatisticsAction = new QAction(
      advancedStatisticsIcon, "Show advanced execution statistics", this);
  m_advancedStatisticsAction->setToolTip(
      "Show functional-unit, memory and pipeline stall statistics");
  connect(m_advancedStatisticsAction, &QAction::triggered, this,
          &ProcessorTab::showAdvancedStatistics);
  m_toolbar->addAction(m_advancedStatisticsAction);

  m_darkmodeAction = new QAction("Processor darkmode", this);
  m_darkmodeAction->setCheckable(true);
  connect(m_darkmodeAction, &QAction::toggled, m_vsrtlWidget,
          [this](bool checked) {
            RipesSettings::setValue(RIPES_SETTING_DARKMODE,
                                    QVariant::fromValue(checked));
            m_vsrtlWidget->setDarkmode(checked);
          });
  m_darkmodeAction->setChecked(
      RipesSettings::value(RIPES_SETTING_DARKMODE).toBool());
}

void ProcessorTab::updateStatistics() {
  static auto lastUpdateTime = std::chrono::system_clock::now();
  static long long lastCycleCount =
      ProcessorHandler::getProcessor()->getCycleCount();

  const auto timeNow = std::chrono::system_clock::now();
  const auto cycleCount = ProcessorHandler::getProcessor()->getCycleCount();
  const auto instrsRetired =
      ProcessorHandler::getProcessor()->getInstructionsRetired();
  const auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
                            timeNow - lastUpdateTime)
                            .count() /
                        1000.0; // in seconds
  const auto cycleDiff = cycleCount - lastCycleCount;

  // Cycle count
  m_ui->cycleCount->setText(QString::number(cycleCount));
  // Instructions retired
  m_ui->instructionsRetired->setText(QString::number(instrsRetired));
  QString cpiText, ipcText;
  if (cycleCount != 0 && instrsRetired != 0) {
    const double cpi =
        static_cast<double>(cycleCount) / static_cast<double>(instrsRetired);
    const double ipc = 1 / cpi;
    cpiText = QString::number(cpi, 'g', 3);
    ipcText = QString::number(ipc, 'g', 3);
  }
  // CPI & IPC
  m_ui->cpi->setText(cpiText);
  m_ui->ipc->setText(ipcText);

  // Clock rate
  const double clockRate = static_cast<double>(cycleDiff) / timeDiff;
  m_ui->clockRate->setText(convertToSIUnits(clockRate) + "Hz");

  // Record timestamp values
  lastUpdateTime = timeNow;
  lastCycleCount = cycleCount;
  updateAdvancedStatistics();
}

void ProcessorTab::updateAdvancedStatistics() {
  if (!m_advancedStatisticsTable || !m_advancedStatisticsSummary)
    return;

  const auto *processor = ProcessorHandler::getProcessor();
  const auto statistics = processor->advancedExecutionStatistics();
  const auto cycles = std::max<long long>(0, processor->getCycleCount());
  const auto instructions =
      std::max<long long>(0, processor->getInstructionsRetired());

  if (!statistics.available) {
    m_advancedStatisticsSummary->setText(
        "Advanced statistics are not available for this processor.");
    m_advancedStatisticsTable->setEnabled(false);
    return;
  }
  m_advancedStatisticsTable->setEnabled(true);
  const int maximumSearchCycle = static_cast<int>(std::min<long long>(
      cycles, std::numeric_limits<int>::max()));
  if (m_stallCycleSearch->value() > maximumSearchCycle) {
    m_stallCycleSearch->setValue(0);
    m_stallHistoryChart->setSelectedCycle(0);
  }
  m_stallCycleSearch->setMaximum(maximumSearchCycle);
  const int maximumRangeCycle = std::max(1, maximumSearchCycle);
  m_statisticsRangeStart->setMaximum(maximumRangeCycle);
  m_statisticsRangeEnd->setMaximum(maximumRangeCycle);
  if (!m_statisticsRangeEnabled && !m_statisticsRangeEnd->hasFocus())
    m_statisticsRangeEnd->setValue(maximumRangeCycle);
  m_stallHistoryChart->setHistories(
      statistics.dataHazardStallHistory,
      statistics.structuralHazardStallHistory,
      statistics.controlHazardStallHistory);

  const uint64_t totalCycles = static_cast<uint64_t>(cycles);
  uint64_t rangeFirst = 1;
  uint64_t rangeLast = totalCycles;
  if (m_statisticsRangeEnabled && totalCycles > 0) {
    rangeFirst = std::min<uint64_t>(m_statisticsRangeStart->value(),
                                    totalCycles);
    rangeLast = std::min<uint64_t>(m_statisticsRangeEnd->value(),
                                   totalCycles);
    if (rangeFirst > rangeLast)
      std::swap(rangeFirst, rangeLast);
    m_stallHistoryChart->setCycleRange(rangeFirst, rangeLast);
  } else {
    m_stallHistoryChart->setCycleRange(0, 0);
  }

  AdvancedExecutionStatisticsSample displayed;
  uint64_t displayedCycles = totalCycles;
  if (m_statisticsRangeEnabled && totalCycles > 0 &&
      rangeLast <= statistics.cycleHistory.size()) {
    const auto &after = statistics.cycleHistory.at(rangeLast - 1);
    const AdvancedExecutionStatisticsSample before =
        rangeFirst <= 1 ? AdvancedExecutionStatisticsSample{}
                        : statistics.cycleHistory.at(rangeFirst - 2);
    displayedCycles = rangeLast - rangeFirst + 1;
    displayed.instructionMemoryCycles =
        after.instructionMemoryCycles - before.instructionMemoryCycles;
    displayed.dataMemoryCycles =
        after.dataMemoryCycles - before.dataMemoryCycles;
    displayed.aluCycles = after.aluCycles - before.aluCycles;
    displayed.integerUnitCycles =
        after.integerUnitCycles - before.integerUnitCycles;
    for (std::size_t i = 0;
         i < displayed.fpAddSubUnitCyclesByInstance.size(); ++i) {
      displayed.fpAddSubUnitCyclesByInstance[i] =
          after.fpAddSubUnitCyclesByInstance[i] -
          before.fpAddSubUnitCyclesByInstance[i];
      displayed.fpMultiplyUnitCyclesByInstance[i] =
          after.fpMultiplyUnitCyclesByInstance[i] -
          before.fpMultiplyUnitCyclesByInstance[i];
      displayed.fpDivideUnitCyclesByInstance[i] =
          after.fpDivideUnitCyclesByInstance[i] -
          before.fpDivideUnitCyclesByInstance[i];
    }
    displayed.stallCycles = after.stallCycles - before.stallCycles;
    displayed.dataHazardStallCycles =
        after.dataHazardStallCycles - before.dataHazardStallCycles;
    displayed.structuralHazardStallCycles =
        after.structuralHazardStallCycles -
        before.structuralHazardStallCycles;
    displayed.controlHazardStallCycles =
        after.controlHazardStallCycles - before.controlHazardStallCycles;
  } else {
    displayed.instructionMemoryCycles = statistics.instructionMemoryCycles;
    displayed.dataMemoryCycles = statistics.dataMemoryCycles;
    displayed.aluCycles = statistics.aluCycles;
    displayed.integerUnitCycles = statistics.integerUnitCycles;
    displayed.fpAddSubUnitCyclesByInstance =
        statistics.fpAddSubUnitCyclesByInstance;
    displayed.fpMultiplyUnitCyclesByInstance =
        statistics.fpMultiplyUnitCyclesByInstance;
    displayed.fpDivideUnitCyclesByInstance =
        statistics.fpDivideUnitCyclesByInstance;
    displayed.stallCycles = statistics.stallCycles;
    displayed.dataHazardStallCycles = statistics.dataHazardStallCycles;
    displayed.structuralHazardStallCycles =
        statistics.structuralHazardStallCycles;
    displayed.controlHazardStallCycles =
        statistics.controlHazardStallCycles;
  }

  const QString cpi =
      instructions == 0
          ? "-"
          : QString::number(static_cast<double>(cycles) / instructions, 'f',
                            3);
  const QString rangeSummary =
      m_statisticsRangeEnabled && totalCycles > 0
          ? QString(" &nbsp;&nbsp; Range: <b>%1–%2</b>")
                .arg(rangeFirst)
                .arg(rangeLast)
          : QString();
  m_advancedStatisticsSummary->setText(
      QString("Instructions: <b>%1</b> &nbsp;&nbsp; Cycles: <b>%2</b> "
              "&nbsp;&nbsp; CPI: <b>%3</b>%4")
          .arg(instructions)
          .arg(cycles)
          .arg(cpi)
          .arg(rangeSummary));

  struct Row {
    QString name;
    uint64_t value;
    uint64_t capacity;
    bool section = false;
  };
  std::vector<Row> rows{
      {"Instruction memory", displayed.instructionMemoryCycles,
       displayedCycles},
      {"Data memory", displayed.dataMemoryCycles, displayedCycles},
      {"ALU (any functional unit)", displayed.aluCycles, displayedCycles,
       true},
      {"  Integer unit 1", displayed.integerUnitCycles, displayedCycles},
  };

  for (unsigned i = 0; i < statistics.fpAddSubUnitCount; ++i) {
    rows.push_back({QString("  FP add/sub unit %1 (%2 cycles)")
                        .arg(i + 1)
                        .arg(statistics.fpAddSubLatency),
                    displayed.fpAddSubUnitCyclesByInstance.at(i),
                    displayedCycles});
  }
  for (unsigned i = 0; i < statistics.fpMultiplyUnitCount; ++i) {
    rows.push_back({QString("  FP multiply unit %1 (%2 cycles)")
                        .arg(i + 1)
                        .arg(statistics.fpMultiplyLatency),
                    displayed.fpMultiplyUnitCyclesByInstance.at(i),
                    displayedCycles});
  }
  for (unsigned i = 0; i < statistics.fpDivideUnitCount; ++i) {
    rows.push_back({QString("  FP divide/sqrt unit %1 (%2 cycles)")
                        .arg(i + 1)
                        .arg(statistics.fpDivideLatency),
                    displayed.fpDivideUnitCyclesByInstance.at(i),
                    displayedCycles});
  }
  rows.push_back(
      {"Stalls", displayed.stallCycles, displayedCycles, true});
  rows.push_back(
      {"  Data hazards (RAW)", displayed.dataHazardStallCycles,
       displayedCycles});
  rows.push_back({"  Structural hazards",
                  displayed.structuralHazardStallCycles, displayedCycles});
  rows.push_back({"  Control hazards", displayed.controlHazardStallCycles,
                  displayedCycles});

  m_advancedStatisticsTable->setRowCount(static_cast<int>(rows.size()));

  for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
    const auto &entry = rows.at(row);
    const QString percentage =
        entry.capacity == 0
            ? "0.00"
            : QString::number(100.0 * static_cast<double>(entry.value) /
                                  static_cast<double>(entry.capacity),
                              'f', 2);
    auto *nameItem = new QTableWidgetItem(entry.name);
    auto *cyclesItem =
        new QTableWidgetItem(QString::number(entry.value));
    auto *percentageItem = new QTableWidgetItem(percentage);
    cyclesItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    percentageItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (entry.section) {
      QFont sectionFont = nameItem->font();
      sectionFont.setBold(true);
      nameItem->setFont(sectionFont);
      cyclesItem->setFont(sectionFont);
      percentageItem->setFont(sectionFont);
    }
    m_advancedStatisticsTable->setItem(row, 0, nameItem);
    m_advancedStatisticsTable->setItem(row, 1, cyclesItem);
    m_advancedStatisticsTable->setItem(row, 2, percentageItem);
  }
}

void ProcessorTab::pause() {
  m_autoClockAction->setChecked(false);
  m_runAction->setChecked(false);
  m_reverseAction->setEnabled(m_vsrtlWidget->isReversible());
}

void ProcessorTab::fitToScreen() { m_vsrtlWidget->zoomToFit(); }

void ProcessorTab::loadProcessorToWidget(const Layout *layout) {
  const bool doPlaceAndRoute = layout != nullptr;
  ProcessorHandler::loadProcessorToWidget(m_vsrtlWidget, doPlaceAndRoute);

  // Construct stage instruction labels
  auto *topLevelComponent = m_vsrtlWidget->getTopLevelComponent();

  m_stageInstructionLabels.clear();
  for (auto laneIt : ProcessorHandler::getProcessor()->structure()) {
    for (unsigned stageIdx = 0; stageIdx < laneIt.second; stageIdx++) {
      StageIndex sid = {laneIt.first, stageIdx};
      auto *stagelabel = new vsrtl::Label(topLevelComponent, "-");
      stagelabel->setPointSize(14);
      m_stageInstructionLabels[sid] = stagelabel;
    }
  }
  if (layout != nullptr) {
    loadLayout(*layout);
  }
  updateInstructionLabels();
  fitToScreen();
}

void ProcessorTab::processorSelection() {
  m_autoClockAction->setChecked(false);
  ProcessorSelectionDialog diag;
  if (diag.exec()) {
    // New processor model was selected
    m_vsrtlWidget->clearDesign();
    m_stageInstructionLabels.clear();
    ProcessorHandler::selectProcessor(diag.getSelectedId(),
                                      diag.getEnabledExtensions(),
                                      diag.getRegisterInitialization());

    // Store selected layout index
    const auto &layouts =
        ProcessorRegistry::getDescription(diag.getSelectedId()).layouts;
    if (auto *layout = diag.getSelectedLayout()) {
      auto layoutIter = std::find(layouts.begin(), layouts.end(), *layout);
      Q_ASSERT(layoutIter != layouts.end());
      const long layoutIndex = std::distance(layouts.begin(), layoutIter);
      RipesSettings::setValue(RIPES_SETTING_PROCESSOR_LAYOUT_ID,
                              static_cast<int>(layoutIndex));
    }

    if (ProcessorHandler::isVSRTLProcessor()) {
      loadProcessorToWidget(diag.getSelectedLayout());
    }
    updateInstructionModel();

    // Retrigger value display action if enabled
    if (m_displayValuesAction->isChecked()) {
      m_vsrtlWidget->setOutputPortValuesVisible(true);
    }
  }
}


void ProcessorTab::cacheSelection() {
  CacheSelectionDialog dialog;
  if (dialog.exec() == QDialog::Accepted) {
    CacheConfigType selectedType = dialog.getSelectedCacheType();
    RipesSettings::setValue("CacheTypeSelected", static_cast<int>(selectedType));
    qDebug() << "Cache type selected:" << static_cast<int>(selectedType);
    //ProcessorHandler::reset(); // para forzar el reinicio por si acaso es necesario
    
    emit cacheConfigurationChanged();
    
  }
}

void ProcessorTab::updateInstructionModel() {
  auto *oldModel = m_instrModel;
  m_instrModel = new InstructionModel(this);

  // Update the instruction view according to the newly created model
  m_ui->instructionView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_ui->instructionView->setModel(m_instrModel);

  // Only the instruction column should stretch
  m_ui->instructionView->horizontalHeader()->setMinimumSectionSize(1);
  m_ui->instructionView->horizontalHeader()->setSectionResizeMode(
      InstructionModel::Breakpoint, QHeaderView::ResizeToContents);
  m_ui->instructionView->horizontalHeader()->setSectionResizeMode(
      InstructionModel::PC, QHeaderView::ResizeToContents);
  // The "stage" section is _NOT_ resized to contents. Resize to contents is
  // very slow if # of items in the model is large and the contents of the rows
  // change frequently.
  m_ui->instructionView->horizontalHeader()->setSectionResizeMode(
      InstructionModel::Stage, QHeaderView::Interactive);
  auto ivfm = QFontMetrics(m_ui->instructionView->font());
  m_ui->instructionView->horizontalHeader()->resizeSection(
      InstructionModel::Stage,
      ivfm.horizontalAdvance(m_instrModel
                                 ->headerData(InstructionModel::Stage,
                                              Qt::Horizontal, Qt::DisplayRole)
                                 .toString()) *
          1.25);
  m_ui->instructionView->horizontalHeader()->setSectionResizeMode(
      InstructionModel::Instruction, QHeaderView::Stretch);
  // Make the instruction view follow the instruction which is currently present
  // in the first stage of the
  connect(m_instrModel, &InstructionModel::firstStageInstrChanged, this,
          &ProcessorTab::setInstructionViewCenterRow);

  if (oldModel) {
    delete oldModel;
  }
}

void ProcessorTab::restart() {
  // Invoked when changes to binary simulation file has been made
  enableSimulatorControls();
}

ProcessorTab::~ProcessorTab() { delete m_ui; }

void ProcessorTab::processorFinished() {
  // Disallow further clocking of the circuit
  m_clockAction->setEnabled(false);
  m_autoClockAction->setChecked(false);
  m_autoClockAction->setEnabled(false);
  m_runAction->setEnabled(false);
  m_runAction->setChecked(false);
}

void ProcessorTab::enableSimulatorControls() {
  m_clockAction->setEnabled(true);
  m_autoClockAction->setEnabled(true);
  m_runAction->setEnabled(true);
  m_reverseAction->setEnabled(m_vsrtlWidget->isReversible());
  m_resetAction->setEnabled(true);
  m_goToCycleAction->setEnabled(true);
  m_targetCycle->setEnabled(true);
  m_pipelineDiagramAction->setEnabled(true);
  m_fpUnicicleDiagramAction->setEnabled(true);
  m_advancedStatisticsAction->setEnabled(true);
}

void ProcessorTab::updateInstructionLabels() {
  const auto &proc = ProcessorHandler::getProcessor();
  for (auto sid : ProcessorHandler::getProcessor()->structure().stageIt()) {
    if (!m_stageInstructionLabels.count(sid))
      continue;
    const auto stageInfo = proc->stageInfo(sid);
    auto &instrLabel = m_stageInstructionLabels.at(sid);
    QString instrString;
    if (stageInfo.state != StageInfo::State::None) {
      /* clang-format off */
            switch (stageInfo.state) {
                case StageInfo::State::Flushed: instrString = "nop (flush)"; break;
                case StageInfo::State::Stalled: instrString = "nop (stall)"; break;
                case StageInfo::State::WayHazard: if(stageInfo.stage_valid) {instrString = "nop (way hazard)";} break;
                case StageInfo::State::Unused: instrString = "nop (unused)"; break;
                case StageInfo::State::None: Q_UNREACHABLE();
            }
      /* clang-format on */
      instrLabel->forceDefaultTextColor(Qt::red);
    } else if (stageInfo.stage_valid) {
      instrString = ProcessorHandler::disassembleInstr(stageInfo.pc);
      instrLabel->clearForcedDefaultTextColor();
    }
    instrLabel->setText(instrString);
  }
}

void ProcessorTab::reset() {
  m_autoClockAction->setChecked(false);
  enableSimulatorControls();
  SystemIO::printString("\n");
}

void ProcessorTab::setInstructionViewCenterRow(int row) {
  const auto view = m_ui->instructionView;
  const auto rect = view->rect();
  int rowTop = view->indexAt(rect.topLeft()).row();
  int rowBot = view->indexAt(rect.bottomLeft()).row();
  rowBot = rowBot < 0 ? m_instrModel->rowCount() : rowBot;

  const int nItemsVisible = rowBot - rowTop;

  // move scrollbar if if is not visible
  if (row <= rowTop || row >= rowBot) {
    auto scrollbar = view->verticalScrollBar();
    scrollbar->setValue(row - nItemsVisible / 2);
  }
}

void ProcessorTab::runFinished() {
  pause();
  ProcessorHandler::checkProcessorFinished();
  m_vsrtlWidget->sync();
  m_statUpdateTimer->stop();
}

void ProcessorTab::autoClockTimeout() {
  if (ProcessorHandler::checkBreakpoint())
    return;
  ProcessorHandler::clock();
}

void ProcessorTab::goToCycle() {
  if (m_autoClockAction->isChecked())
    m_autoClockAction->setChecked(false);

  auto *processor = ProcessorHandler::getProcessorNonConst();
  if (!processor)
    return;

  auto *vsrtlProcessor = dynamic_cast<vsrtl::SimDesign *>(processor);
  if (vsrtlProcessor)
    vsrtlProcessor->setEnableSignals(false);

  const long long target = m_targetCycle->value();
  if (target < processor->getCycleCount()) {
    const long long distance = processor->getCycleCount() - target;
    const long long stackSize =
        RipesSettings::value(RIPES_SETTING_REWINDSTACKSIZE).toLongLong();
    if (!(processor->features() & RipesProcessor::isReversible) ||
        distance > stackSize) {
      RipesSettings::getObserver(RIPES_GLOBALSIGNAL_REQRESET)->trigger();
      processor = ProcessorHandler::getProcessorNonConst();
      vsrtlProcessor = dynamic_cast<vsrtl::SimDesign *>(processor);
      if (vsrtlProcessor)
        vsrtlProcessor->setEnableSignals(false);
    }
  }

  while (processor->getCycleCount() > target) {
    const long long before = processor->getCycleCount();
    processor->reverseProcessor();
    if (processor->getCycleCount() == before) {
      RipesSettings::getObserver(RIPES_GLOBALSIGNAL_REQRESET)->trigger();
      processor = ProcessorHandler::getProcessorNonConst();
      vsrtlProcessor = dynamic_cast<vsrtl::SimDesign *>(processor);
      if (vsrtlProcessor)
        vsrtlProcessor->setEnableSignals(false);
      break;
    }
  }
  while (processor->getCycleCount() < target && !processor->finished())
    processor->clock();

  if (vsrtlProcessor)
    vsrtlProcessor->setEnableSignals(true);
  updateStatistics();
  updateInstructionLabels();
  m_vsrtlWidget->sync();
  if (processor->finished())
    processorFinished();
  else
    enableSimulatorControls();
  m_targetCycle->setValue(static_cast<int>(std::min<long long>(
      processor->getCycleCount(), std::numeric_limits<int>::max())));
}

void ProcessorTab::autoClock(bool state) {
  const QIcon startAutoClockIcon = QIcon(":/icons/step-clock.svg");
  const QIcon stopAutoTimerIcon = QIcon(":/icons/stop-clock.svg");
  if (!state) {
    m_autoClockTimer->stop();
    m_autoClockAction->setIcon(startAutoClockIcon);
  } else {
    // Always clock the processor to start with. Afterwards, run
    // autoClockTimeout() which will check if the processor is at a breakpoint.
    // This is to circumvent some annoying cross-thread, eventloop,
    // race-condition-y state setting wrt. when exactly a breakpoint is hit.
    ProcessorHandler::clock();
    m_autoClockTimer->start();
    m_autoClockAction->setIcon(stopAutoTimerIcon);
  }

  // Enable/disable all other actions
  m_selectProcessorAction->setEnabled(!state);
  m_clockAction->setEnabled(!state);
  m_reverseAction->setEnabled(!state);
  m_resetAction->setEnabled(!state);
  m_goToCycleAction->setEnabled(!state);
  m_targetCycle->setEnabled(!state);
  m_displayValuesAction->setEnabled(!state);
  m_pipelineDiagramAction->setEnabled(!state);
  m_fpUnicicleDiagramAction->setEnabled(!state);
  m_advancedStatisticsAction->setEnabled(!state);
  m_runAction->setEnabled(!state);
}

void ProcessorTab::run(bool state) {
  // Stop any currently exeuting auto-clocking
  if (m_autoClockAction->isChecked()) {
    m_autoClockAction->setChecked(false);
  }
  if (state) {
    ProcessorHandler::run();
    m_statUpdateTimer->start();
  } else {
    ProcessorHandler::stopRun();
    m_statUpdateTimer->stop();
  }

  // Enable/Disable all actions based on whether the processor is running.
  m_selectProcessorAction->setEnabled(!state);
  m_clockAction->setEnabled(!state);
  m_autoClockAction->setEnabled(!state);
  m_reverseAction->setEnabled(!state);
  m_resetAction->setEnabled(!state);
  m_goToCycleAction->setEnabled(!state);
  m_targetCycle->setEnabled(!state);
  m_displayValuesAction->setEnabled(!state);
  m_pipelineDiagramAction->setEnabled(!state);
  m_fpUnicicleDiagramAction->setEnabled(!state);
  m_advancedStatisticsAction->setEnabled(!state);

  // Disable widgets which are not updated when running the processor
  m_vsrtlWidget->setEnabled(!state);
  m_ui->registerContainerWidget->setEnabled(!state);
  m_ui->instructionView->setEnabled(!state);
}

void ProcessorTab::reverse() {
  m_vsrtlWidget->reverse();
  enableSimulatorControls();
}

void ProcessorTab::showPipelineDiagram() {
  auto w = PipelineDiagramWidget(m_stageModel);
  w.exec();
}

void ProcessorTab::showFPUnicicleDiagram() {
  m_fpUnicicleDiagramWidget->refreshDiagram();
  m_fpUnicicleDiagramWidget->show();
  m_fpUnicicleDiagramWidget->raise();
  m_fpUnicicleDiagramWidget->activateWindow();
}

void ProcessorTab::showAdvancedStatistics() {
  updateAdvancedStatistics();
  m_advancedStatisticsDialog->show();
  m_advancedStatisticsDialog->raise();
  m_advancedStatisticsDialog->activateWindow();
}
} // namespace Ripes
