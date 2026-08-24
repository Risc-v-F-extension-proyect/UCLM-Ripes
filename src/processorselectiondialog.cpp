#include "processorselectiondialog.h"
#include "ui_processorselectiondialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QTimer>

#include "processorhandler.h"
#include "radix.h"
#include "ripessettings.h"

namespace Ripes {

ProcessorSelectionDialog::ProcessorSelectionDialog(QWidget *parent)
    : QDialog(parent), m_ui(new Ui::ProcessorSelectionDialog) {

  m_ui->setupUi(this);
  setWindowTitle("Configure Processor");

  // Set properties for current processor
  m_selectedID = qvariant_cast<ProcessorID>(
      RipesSettings::value(RIPES_SETTING_PROCESSOR_ID));
  const auto &desc = ProcessorRegistry::getDescription(m_selectedID);

  m_selectedISA = desc.isaInfo().isa->isaID();
  m_selectedExtensionsForID[ProcessorHandler::getID()] =
      ProcessorHandler::currentISA()->enabledExtensions();
  m_selectedTags = desc.tags;

  // --- Populating processor options --- //

  QStringList isaList;
  QList<int> xlenList;
  QList<DatapathType> datapathList;

  for (const auto &desc : ProcessorRegistry::getAvailableProcessors()) {
    // Populate ISAs
    const ISA isaID = desc.second->isaInfo().isa->isaID();
    const QString &isaFamily = ISAFamilyNames.at(isaID);
    if (isaList.count(isaFamily) == 0) {
      isaList.append(isaFamily);
      m_ui->isa->addItem(isaFamily, (int)isaID);
    }
    const int isaWidth = desc.second->isaInfo().isa->bits();
    if (xlenList.count(isaWidth) == 0) {
      xlenList.append(isaWidth);
      m_ui->xlen->addItem(QString::number(isaWidth) + "-bit", isaWidth);
    }

    // Populate main datapath variants
    const DatapathType datapath = desc.second->tags.datapathType;
    const QString datapathName = DatapathNames.at(datapath);
    if (datapathList.count(datapath) == 0) {
      datapathList.append(datapath);
      m_ui->datapath->addItem(datapathName, (int)datapath);
    }

    // Initialize selected extensions for processors while we're here
    m_selectedExtensionsForID[desc.second->id] =
        desc.second->isaInfo().defaultExtensions;
  }

  // Set selected extensions for current processor
  m_selectedExtensionsForID[ProcessorHandler::getID()] =
      ProcessorHandler::currentISA()->enabledExtensions();

  // Populate processor variant options
  populateVariants();

  // Populate register initialisations
  m_ui->regInitWidget->processorSelectionChanged(m_selectedID);
  setupFALULatencyOptions();

  // Populate processor layouts
  for (const auto &layout : desc.layouts) {
    m_ui->layout->addItem(layout.name);
  }

  // --- Setting initial form state --- //

  // ISA -- adjust for isa selection hack
  m_ui->xlen->setCurrentIndex(m_ui->xlen->findData(desc.isaInfo().isa->bits()));
  m_ui->isa->setCurrentIndex(m_ui->isa->findData(
      (int)desc.isaInfo().isa->isaID() - m_ui->xlen->currentIndex()));
  // Datapath
  m_ui->datapath->setCurrentIndex(
      m_ui->datapath->findData(desc.tags.datapathType));
  m_ui->hasForwarding->setChecked(desc.tags.hasForwarding);
  m_ui->hasHazardDetection->setChecked(desc.tags.hasHazardDetection);
  // Branches
  m_ui->branchStrategy->setCurrentIndex(
      m_ui->branchStrategy->findData(desc.tags.branchStrategy));
  m_ui->branchSlots->setCurrentIndex(
      m_ui->branchSlots->findData(desc.tags.branchDelaySlots));
  // Description
  m_ui->description->setText(desc.description);

  // Update extension checkboxes
  auto isaInfo = desc.isaInfo();
  for (const auto &ext : std::as_const(isaInfo.supportedExtensions)) {
    auto chkbox = new QCheckBox(ext);
    chkbox->setToolTip(isaInfo.isa->extensionDescription(ext));
    m_ui->extensions->addWidget(chkbox);
    if (m_selectedExtensionsForID[desc.id].contains(ext)) {
      chkbox->setChecked(true);
    }
    // Connect checkbox toggle events
    connect(chkbox, &QCheckBox::toggled, this, [this, ext](bool toggled) {
      handleExtensionToggled(ext, toggled);
      updateFALULatencyOptionsEnabled();
    });
  }

  // Disable options if there are no more available ones for current config
  setEnabledVariants();
  updateFALULatencyOptionsEnabled();

  // Set current layout
  unsigned layoutID =
      RipesSettings::value(RIPES_SETTING_PROCESSOR_LAYOUT_ID).toInt();
  if (layoutID >= ProcessorRegistry::getDescription(ProcessorHandler::getID())
                      .layouts.size()) {
    layoutID = 0;
  }
  m_ui->layout->setCurrentIndex(layoutID);

  // Poll form state every 50 ms
  QTimer *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this,
          &ProcessorSelectionDialog::updateSelectedTags);
  timer->start(50);

  // If selected tags change, get corresponding processor and update dialog info
  connect(this, &ProcessorSelectionDialog::selectionChanged, this,
          &ProcessorSelectionDialog::updateDialog);

  connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, [this] {
    RipesSettings::setValue(RIPES_SETTING_RV5S_FALU_ADDSUB_LATENCY,
                            getFALUAddSubLatency());
    RipesSettings::setValue(RIPES_SETTING_RV5S_FALU_MUL_LATENCY,
                            getFALUMulLatency());
    RipesSettings::setValue(RIPES_SETTING_RV5S_FALU_DIV_LATENCY,
                            getFALUDivLatency());
    RipesSettings::setValue(RIPES_SETTING_RV5S_FALU_ADDSUB_COUNT,
                            getFALUAddSubCount());
    RipesSettings::setValue(RIPES_SETTING_RV5S_FALU_MUL_COUNT,
                            getFALUMulCount());
    RipesSettings::setValue(RIPES_SETTING_RV5S_FALU_DIV_COUNT,
                            getFALUDivCount());
    RipesSettings::setValue(RIPES_SETTING_RV5S_FALU_ADDSUB_PIPELINED,
                            getFALUAddSubPipelined());
    RipesSettings::setValue(RIPES_SETTING_RV5S_FALU_MUL_PIPELINED,
                            getFALUMulPipelined());
    RipesSettings::setValue(RIPES_SETTING_RV5S_FALU_DIV_PIPELINED,
                            getFALUDivPipelined());
    accept();
  });
  connect(m_ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ProcessorSelectionDialog::~ProcessorSelectionDialog() { delete m_ui; }

QStringList ProcessorSelectionDialog::getEnabledExtensions() const {
  return m_selectedExtensionsForID.at(m_selectedID);
}

RegisterInitialization
ProcessorSelectionDialog::getRegisterInitialization() const {
  return m_ui->regInitWidget->getInitialization();
}

const Layout *ProcessorSelectionDialog::getSelectedLayout() const {
  const auto &desc =
      ProcessorRegistry::getAvailableProcessors().at(m_selectedID);
  auto it = llvm::find_if(desc->layouts, [&](const auto &layout) {
    return layout.name == m_ui->layout->currentText();
  });
  if (it != desc->layouts.end())
    return &*it;
  return nullptr;
}

unsigned ProcessorSelectionDialog::getFALUAddSubLatency() const {
  return m_faluAddSubLatency->value();
}

unsigned ProcessorSelectionDialog::getFALUMulLatency() const {
  return m_faluMulLatency->value();
}

unsigned ProcessorSelectionDialog::getFALUDivLatency() const {
  return m_faluDivLatency->value();
}

unsigned ProcessorSelectionDialog::getFALUAddSubCount() const {
  return m_faluAddSubCount->value();
}

unsigned ProcessorSelectionDialog::getFALUMulCount() const {
  return m_faluMulCount->value();
}

unsigned ProcessorSelectionDialog::getFALUDivCount() const {
  return m_faluDivCount->value();
}

bool ProcessorSelectionDialog::getFALUAddSubPipelined() const {
  return m_faluAddSubPipelined->isChecked();
}

bool ProcessorSelectionDialog::getFALUMulPipelined() const {
  return m_faluMulPipelined->isChecked();
}

bool ProcessorSelectionDialog::getFALUDivPipelined() const {
  return m_faluDivPipelined->isChecked();
}

void ProcessorSelectionDialog::updateSelectedTags() {
  if (m_ui->xlen->currentData().toInt() == 32) {
    disableDExtensionForCurrentSelection();
  }

  ISA selectedISA = // hacky hack
      (ISA)(m_ui->isa->currentData().toInt() + m_ui->xlen->currentIndex());
  DatapathType selectedDatapath =
      m_ui->datapath->currentData().value<DatapathType>();
  BranchStrategy selectedStrat =
      m_ui->branchStrategy->currentData().value<BranchStrategy>();
  BranchDelaySlots selectedSlots =
      m_ui->branchSlots->currentData().value<BranchDelaySlots>();
  ProcessorTags selectedTags = {selectedDatapath, selectedStrat, selectedSlots,
                                m_ui->hasForwarding->isChecked(),
                                m_ui->hasHazardDetection->isChecked()};

  if (m_selectedISA != selectedISA || m_selectedTags != selectedTags) {
    // Check that a processor exists with the selected properties
    if (ProcessorRegistry::getProcessor(selectedISA, selectedTags).size() > 0) {
      m_selectedISA = selectedISA;
      m_selectedTags = selectedTags;
    }
    // Otherwise, redirect to the closest valid processor
    else {
      const auto &desc = ProcessorRegistry::getDescription(
          redirectToValidProcessor(selectedISA, selectedTags));
      selectedISA = desc.isaInfo().isa->isaID();
      selectedTags = desc.tags;
      m_selectedISA = selectedISA;
      m_selectedTags = selectedTags;
    }

    emit selectionChanged(selectedISA, selectedTags);
    updateFALULatencyOptionsEnabled();
  }
}

void ProcessorSelectionDialog::updateDialog(ISA isa, ProcessorTags tags) {
  QList<ProcessorID> selected = ProcessorRegistry::getProcessor(isa, tags);

  // Check valid selection and update selected processor
  m_ui->buttonBox->button(QDialogButtonBox::Ok)
      ->setEnabled(selected.size() == 1);
  if (selected.isEmpty()) {
    m_ui->description->setText(
        "Your selection does not correspond to any available processor.");
    m_ui->description->setEnabled(false);
    return;
  }
  if (selected.size() > 1) {
    m_ui->description->setText(
        "There are multiple processors defined with these properties.\n"
        "Check src/processorregistry.cpp");
    m_ui->description->setEnabled(false);
    return;
  }

  m_selectedID = selected[0];
  const auto &desc = ProcessorRegistry::getDescription(m_selectedID);

  m_selectedISA = desc.isaInfo().isa->isaID();
  m_selectedTags = desc.tags;

  // --- Update dialog state --- //
  m_ui->description->setEnabled(true);

  // Setup extensions; Clear previously selected extensions and add whatever
  // extensions are supported for the selected processor
  QLayoutItem *item;
  while ((item = m_ui->extensions->layout()->takeAt(0)) != nullptr) {
    delete item->widget();
    delete item;
  }

  auto isaInfo = desc.isaInfo();
  for (const auto &ext : std::as_const(isaInfo.supportedExtensions)) {
    auto chkbox = new QCheckBox(ext);
    chkbox->setToolTip(isaInfo.isa->extensionDescription(ext));
    m_ui->extensions->addWidget(chkbox);
    if (m_selectedExtensionsForID[desc.id].contains(ext)) {
      chkbox->setChecked(true);
    }
    connect(chkbox, &QCheckBox::toggled, this, [this, ext](bool toggled) {
      handleExtensionToggled(ext, toggled);
      updateFALULatencyOptionsEnabled();
    });
  }

  // Regenerate available processor variants
  m_ui->hasForwarding->setChecked(desc.tags.hasForwarding);
  m_ui->hasHazardDetection->setChecked(desc.tags.hasHazardDetection);

  m_ui->branchStrategy->clear();
  m_ui->branchSlots->clear();
  populateVariants();
  m_ui->branchStrategy->setCurrentIndex(
      m_ui->branchStrategy->findData(desc.tags.branchStrategy));
  m_ui->branchSlots->setCurrentIndex(
      m_ui->branchSlots->findData(desc.tags.branchDelaySlots));
  setEnabledVariants();
  updateFALULatencyOptionsEnabled();

  // Set description
  m_ui->description->setText(desc.description);

  // Regenerate register initialisations
  m_ui->regInitWidget->processorSelectionChanged(m_selectedID);

  // Regenerate available layouts
  m_ui->layout->clear();
  for (const auto &layout : desc.layouts) {
    m_ui->layout->addItem(layout.name);
  }
}

void ProcessorSelectionDialog::setupFALULatencyOptions() {
  auto *label = new QLabel("FP ALUs:");
  label->setAlignment(Qt::AlignRight | Qt::AlignTop);
  label->setContentsMargins(27, 12, 0, 0);
  m_faluLatencyWidget = new QWidget(this);
  auto *grid = new QGridLayout(m_faluLatencyWidget);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(10);
  grid->setVerticalSpacing(4);

  const QStringList headers = {"Type", "Latency", "Amount", "Seg."};
  for (int column = 0; column < headers.size(); ++column) {
    auto *header = new QLabel(headers.at(column));
    header->setContentsMargins(0, 12, 0, 0);
    grid->addWidget(header, 0, column);
  }

  auto addUnitRow = [grid](int row, const QString &name,
                           QSpinBox *&latency, const QString &latencySetting,
                           unsigned maximumLatency,
                           QSpinBox *&count, const QString &countSetting,
                           QCheckBox *&segmented,
                           const QString &segmentedSetting) {
    grid->addWidget(new QLabel(name), row, 0);
    latency = new QSpinBox;
    // A one-cycle FP operation can publish its anticipated result on the same
    // edge where a dependent instruction captures its operand. The current
    // ID/EX forwarding scheme therefore requires at least two cycles.
    latency->setRange(2, maximumLatency);
    latency->setValue(RipesSettings::value(latencySetting).toUInt());
    grid->addWidget(latency, row, 1);
    count = new QSpinBox;
    count->setRange(1, 4);
    count->setValue(RipesSettings::value(countSetting).toUInt());
    grid->addWidget(count, row, 2);
    segmented = new QCheckBox;
    segmented->setChecked(RipesSettings::value(segmentedSetting).toBool());
    grid->addWidget(segmented, row, 3);

    const auto updateCountAvailability = [count](bool isSegmented) {
      if (isSegmented)
        count->setValue(1);
      count->setEnabled(!isSegmented);
    };
    QObject::connect(segmented, &QCheckBox::toggled,
                     updateCountAvailability);
    updateCountAvailability(segmented->isChecked());
  };

<<<<<<< Updated upstream
  addUnitRow(1, "add/sub", m_faluAddSubLatency,
             RIPES_SETTING_RV5S_FALU_ADDSUB_LATENCY, 7, m_faluAddSubCount,
             RIPES_SETTING_RV5S_FALU_ADDSUB_COUNT, m_faluAddSubPipelined,
             RIPES_SETTING_RV5S_FALU_ADDSUB_PIPELINED);
  addUnitRow(2, "mul", m_faluMulLatency,
             RIPES_SETTING_RV5S_FALU_MUL_LATENCY, 12, m_faluMulCount,
             RIPES_SETTING_RV5S_FALU_MUL_COUNT, m_faluMulPipelined,
             RIPES_SETTING_RV5S_FALU_MUL_PIPELINED);
  addUnitRow(3, "div", m_faluDivLatency,
=======
  addUnitRow(1, "Add", m_faluAddSubLatency,
             RIPES_SETTING_RV5S_FALU_ADDSUB_LATENCY, 7, m_faluAddSubCount,
             RIPES_SETTING_RV5S_FALU_ADDSUB_COUNT, m_faluAddSubPipelined,
             RIPES_SETTING_RV5S_FALU_ADDSUB_PIPELINED);
  addUnitRow(2, "Mul", m_faluMulLatency,
             RIPES_SETTING_RV5S_FALU_MUL_LATENCY, 12, m_faluMulCount,
             RIPES_SETTING_RV5S_FALU_MUL_COUNT, m_faluMulPipelined,
             RIPES_SETTING_RV5S_FALU_MUL_PIPELINED);
  addUnitRow(3, "Div", m_faluDivLatency,
>>>>>>> Stashed changes
             RIPES_SETTING_RV5S_FALU_DIV_LATENCY, 30, m_faluDivCount,
             RIPES_SETTING_RV5S_FALU_DIV_COUNT, m_faluDivPipelined,
             RIPES_SETTING_RV5S_FALU_DIV_PIPELINED);

  m_ui->configForm->addRow(label, m_faluLatencyWidget);
}

void ProcessorSelectionDialog::updateFALULatencyOptionsEnabled() {
  if (!m_faluLatencyWidget) {
    return;
  }

  const bool isFiveStage = m_selectedTags.datapathType == DatapathType::P_5S;
  const bool hasFExtension =
      m_selectedExtensionsForID[m_selectedID].contains("F");
  m_faluLatencyWidget->setEnabled(isFiveStage && hasFExtension);
}

void ProcessorSelectionDialog::handleExtensionToggled(const QString &ext,
                                                      bool toggled) {
  QStringList &extensions = m_selectedExtensionsForID[m_selectedID];

  if (toggled) {
    if (!extensions.contains(ext)) {
      extensions << ext;
    }
  } else {
    extensions.removeAll(ext);
    if (ext == "F") {
      extensions.removeAll("D");
      setExtensionCheckboxChecked("D", false);
    }
  }

  if (ext == "D" && toggled) {
    enableDExtensionDependencies();
  }
}

void ProcessorSelectionDialog::enableDExtensionDependencies() {
  QStringList &extensions = m_selectedExtensionsForID[m_selectedID];
  if (!extensions.contains("D")) {
    return;
  }

  if (!extensions.contains("F")) {
    extensions << "F";
  }
  setExtensionCheckboxChecked("F", true);

  if (m_ui->xlen->currentData().toInt() == 64) {
    return;
  }

  const int rv64Index = m_ui->xlen->findData(64);
  if (rv64Index < 0) {
    return;
  }

  m_ui->xlen->setCurrentIndex(rv64Index);

  const ISA rv64ISA = static_cast<ISA>(
      m_ui->isa->currentData().toInt() + m_ui->xlen->currentIndex());
  const QList<ProcessorID> rv64Processors =
      ProcessorRegistry::getProcessor(rv64ISA, m_selectedTags);
  for (const ProcessorID id : rv64Processors) {
    QStringList &rv64Extensions = m_selectedExtensionsForID[id];
    if (!rv64Extensions.contains("D")) {
      rv64Extensions << "D";
    }
    if (!rv64Extensions.contains("F")) {
      rv64Extensions << "F";
    }
  }
}

void ProcessorSelectionDialog::disableDExtensionForCurrentSelection() {
  for (const auto &desc : ProcessorRegistry::getAvailableProcessors()) {
    if (desc.second->isaInfo().isa->bits() == 32) {
      m_selectedExtensionsForID[desc.first].removeAll("D");
    }
  }
  setExtensionCheckboxChecked("D", false);
}

void ProcessorSelectionDialog::setExtensionCheckboxChecked(const QString &ext,
                                                           bool checked) {
  for (int i = 0; i < m_ui->extensions->layout()->count(); ++i) {
    QLayoutItem *item = m_ui->extensions->layout()->itemAt(i);
    auto *checkbox = qobject_cast<QCheckBox *>(item->widget());
    if (checkbox && checkbox->text() == ext) {
      if (checkbox->isChecked() != checked) {
        const QSignalBlocker blocker(checkbox);
        checkbox->setChecked(checked);
      }
      return;
    }
  }
}

void ProcessorSelectionDialog::populateVariants() {
  QList<BranchStrategy> branchList;
  QList<BranchDelaySlots> slotsList;

  for (const auto &desc : ProcessorRegistry::getAvailableProcessors()) {
    if (desc.second->tags.datapathType == m_selectedTags.datapathType) {
      const BranchStrategy branchStrat = desc.second->tags.branchStrategy;
      const QString branchName = BranchNames.at(branchStrat);
      if (branchList.count(branchStrat) == 0) {
        branchList.append(branchStrat);
        m_ui->branchStrategy->addItem(branchName, (int)branchStrat);
      }
      const BranchDelaySlots branchSlots = desc.second->tags.branchDelaySlots;
      if (slotsList.count(branchSlots) == 0) {
        slotsList.append(branchSlots);
        m_ui->branchSlots->addItem(
            branchSlots == 0 ? "" : QString::number(branchSlots) + "-slot",
            (int)branchSlots);
      }
    }
  }
  // Sort branch delay slot options in ascending order
  m_ui->branchSlots->model()->sort(0);
}

void ProcessorSelectionDialog::setEnabledVariants() {
  const auto &desc = ProcessorRegistry::getDescription(m_selectedID);
  bool forwarding = desc.tags.hasForwarding;
  bool hazard = desc.tags.hasHazardDetection;
  BranchStrategy branch = desc.tags.branchStrategy;
  BranchDelaySlots branchSlots = desc.tags.branchDelaySlots;

  m_ui->hasForwarding->setEnabled(false);
  m_ui->hasHazardDetection->setEnabled(false);
  m_ui->branchStrategy->setEnabled(false);
  m_ui->branchSlots->setEnabled(false);

  for (const auto &desc : ProcessorRegistry::getAvailableProcessors()) {
    if (desc.second->tags.datapathType == m_selectedTags.datapathType) {

      if (desc.second->tags.branchStrategy != branch)
        m_ui->branchStrategy->setEnabled(true);
      if (desc.second->tags.branchDelaySlots != branchSlots)
        m_ui->branchSlots->setEnabled(true);

      if (desc.second->tags.branchStrategy == m_selectedTags.branchStrategy &&
          desc.second->tags.branchDelaySlots ==
              m_selectedTags.branchDelaySlots) {
        if (desc.second->tags.hasForwarding != forwarding)
          m_ui->hasForwarding->setEnabled(true);
        if (desc.second->tags.hasHazardDetection != hazard)
          m_ui->hasHazardDetection->setEnabled(true);
      }
    }
  }
}

ProcessorID
ProcessorSelectionDialog::redirectToValidProcessor(ISA isa,
                                                   ProcessorTags tags) {
  QList<ProcessorID> selected = {};
  QList<ProcessorID> availableOptions = {};

  // ~~~ spaghetti time ~~~ //

  // Explore ISAs
  for (const auto &desc : ProcessorRegistry::getAvailableProcessors()) {
    if (isa == desc.second->isaInfo().isa->isaID())
      availableOptions.append(desc.first);
  }
  if (!availableOptions.isEmpty())
    selected = availableOptions;
  if (selected.size() == 1)
    return selected[0];

  // Explore datapaths
  availableOptions = {};
  for (auto id : selected) {
    const auto &desc = ProcessorRegistry::getDescription(id);
    if (tags.datapathType == desc.tags.datapathType)
      availableOptions.append(desc.id);
  }
  if (!availableOptions.isEmpty())
    selected = availableOptions;
  if (selected.size() == 1)
    return selected[0];

  // Explore branch options
  availableOptions = {};
  for (auto id : selected) {
    const auto &desc = ProcessorRegistry::getDescription(id);
    if (tags.branchStrategy == desc.tags.branchStrategy)
      availableOptions.append(desc.id);
  }
  if (!availableOptions.isEmpty())
    selected = availableOptions;
  if (selected.size() == 1)
    return selected[0];

  availableOptions = {};
  for (auto id : selected) {
    const auto &desc = ProcessorRegistry::getDescription(id);
    if (tags.branchDelaySlots == desc.tags.branchDelaySlots)
      availableOptions.append(desc.id);
  }
  if (!availableOptions.isEmpty())
    selected = availableOptions;
  if (selected.size() == 1)
    return selected[0];

  // Explore forwarding/hazard detection
  availableOptions = {};
  for (auto id : selected) {
    const auto &desc = ProcessorRegistry::getDescription(id);
    if (tags.hasForwarding == desc.tags.hasForwarding)
      availableOptions.append(desc.id);
  }
  if (!availableOptions.isEmpty())
    selected = availableOptions;
  if (selected.size() == 1)
    return selected[0];

  availableOptions = {};
  for (auto id : selected) {
    const auto &desc = ProcessorRegistry::getDescription(id);
    if (tags.hasHazardDetection == desc.tags.hasHazardDetection)
      availableOptions.append(desc.id);
  }
  if (!availableOptions.isEmpty())
    selected = availableOptions;
  if (selected.size() == 1)
    return selected[0];

  return selected[0];
}

} // namespace Ripes
