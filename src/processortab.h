#pragma once

#include <QAction>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>
#include <QWidget>

#include "isa/isa_types.h"
#include "processors/interface/ripesprocessor.h"
#include "ripestab.h"

QT_FORWARD_DECLARE_CLASS(QDialog)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QTableWidget)

namespace vsrtl {
class VSRTLWidget;
class Label;
} // namespace vsrtl

namespace Ripes {

namespace Ui {
class ProcessorTab;
}

class InstructionModel;
class RegisterModel;
class PipelineDiagramModel;
class FPUnicicleDiagramWidget;
struct Layout;

class ProcessorTab : public RipesTab {
  friend class RunDialog;
  friend class MainWindow;
  Q_OBJECT

public:
  ProcessorTab(QToolBar *controlToolbar, QToolBar *additionalToolbar,
               QWidget *parent = nullptr);
  ~ProcessorTab() override;

  void initRegWidget();

public slots:
  void pause();
  void restart();
  void reset();
  void reverse();
  void processorFinished();
  void runFinished();
  void updateStatistics();
  void updateInstructionLabels();
  void fitToScreen();

  void processorSelection();
  void cacheSelection();

private slots:
  void run(bool state);
  void autoClock(bool state);
  void autoClockTimeout();
  void goToCycle();
  void setInstructionViewCenterRow(int row);
  void showPipelineDiagram();
  void showFPUnicicleDiagram();
  void showAdvancedStatistics();
  
  // Añado esto para ver si puedo conseguir recargar pestañas desde aquí cuando se cambie la config
 signals: 
  void cacheConfigurationChanged();

private:
  void setupSimulatorActions(QToolBar *controlToolbar);
  void enableSimulatorControls();
  void updateInstructionModel();
  void updateRegisterModel();
  void updateAdvancedStatistics();
  void loadLayout(const Layout &);
  void loadProcessorToWidget(const Layout *);

  Ui::ProcessorTab *m_ui = nullptr;
  InstructionModel *m_instrModel = nullptr;
  PipelineDiagramModel *m_stageModel = nullptr;
  FPUnicicleDiagramWidget *m_fpUnicicleDiagramWidget = nullptr;
  QDialog *m_advancedStatisticsDialog = nullptr;
  QLabel *m_advancedStatisticsSummary = nullptr;
  QTableWidget *m_advancedStatisticsTable = nullptr;

  vsrtl::VSRTLWidget *m_vsrtlWidget = nullptr;

  std::map<StageIndex, vsrtl::Label *> m_stageInstructionLabels;

  QTimer *m_statUpdateTimer;

  // Actions
  QAction *m_selectProcessorAction = nullptr;
  QAction* m_selectCacheAction = nullptr;

  QAction *m_clockAction = nullptr;
  QAction *m_autoClockAction = nullptr;
  QAction *m_runAction = nullptr;
  QAction *m_displayValuesAction = nullptr;
  QAction *m_pipelineDiagramAction = nullptr;
  QAction *m_fpUnicicleDiagramAction = nullptr;
  QAction *m_advancedStatisticsAction = nullptr;
  QAction *m_reverseAction = nullptr;
  QAction *m_resetAction = nullptr;
  QAction *m_darkmodeAction = nullptr;
  QTimer *m_autoClockTimer = nullptr;

  QSpinBox *m_autoClockInterval = nullptr;
  QSpinBox *m_targetCycle = nullptr;
  QAction *m_goToCycleAction = nullptr;
};
} // namespace Ripes
