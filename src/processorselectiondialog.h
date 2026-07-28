#pragma once

#include <QDialog>
#include <QSpinBox>

#include "processorregistry.h"

QT_FORWARD_DECLARE_CLASS(QCheckBox)

namespace Ripes {

namespace Ui {
class ProcessorSelectionDialog;
}

class ProcessorSelectionDialog : public QDialog {
  Q_OBJECT

public:
  explicit ProcessorSelectionDialog(QWidget *parent = nullptr);
  ~ProcessorSelectionDialog();

  QStringList getEnabledExtensions() const;
  RegisterInitialization getRegisterInitialization() const;
  const Layout *getSelectedLayout() const;
  unsigned getFALUAddSubLatency() const;
  unsigned getFALUMulLatency() const;
  unsigned getFALUDivLatency() const;
  unsigned getFALUAddSubCount() const;
  unsigned getFALUMulCount() const;
  unsigned getFALUDivCount() const;
  bool getFALUAddSubPipelined() const;
  bool getFALUMulPipelined() const;
  bool getFALUDivPipelined() const;

  ProcessorID getSelectedId() const { return m_selectedID; }

signals:
  void selectionChanged(ISA isa, ProcessorTags tags) const;

private slots:
  void updateSelectedTags();
  void updateDialog(ISA isa, ProcessorTags tags);

private:
  void populateVariants();
  void setEnabledVariants();
  void setupFALULatencyOptions();
  void updateFALULatencyOptionsEnabled();
  void handleExtensionToggled(const QString &ext, bool toggled);
  void enableDExtensionDependencies();
  void disableDExtensionForCurrentSelection();
  void setExtensionCheckboxChecked(const QString &ext, bool checked);
  ProcessorID redirectToValidProcessor(ISA isa, ProcessorTags tags);

  ISA m_selectedISA;
  ProcessorID m_selectedID;
  ProcessorTags m_selectedTags;
  Ui::ProcessorSelectionDialog *m_ui;
  std::map<ProcessorID, QStringList> m_selectedExtensionsForID;
  QWidget *m_faluLatencyWidget = nullptr;
  QSpinBox *m_faluAddSubLatency = nullptr;
  QSpinBox *m_faluMulLatency = nullptr;
  QSpinBox *m_faluDivLatency = nullptr;
  QSpinBox *m_faluAddSubCount = nullptr;
  QSpinBox *m_faluMulCount = nullptr;
  QSpinBox *m_faluDivCount = nullptr;
  QCheckBox *m_faluAddSubPipelined = nullptr;
  QCheckBox *m_faluMulPipelined = nullptr;
  QCheckBox *m_faluDivPipelined = nullptr;
};
} // namespace Ripes
