#include "pipelinediagrammodel.h"

#include "processorhandler.h"
#include "ripessettings.h"

#include <algorithm>
#include <vector>

namespace Ripes {

static AInt indexToAddress(unsigned index) {
  if (auto spt = ProcessorHandler::getProgram()) {
    return (index * ProcessorHandler::currentISA()->instrBytes()) +
           spt->getSection(TEXT_SECTION_NAME)->address;
  }
  return 0;
}

PipelineDiagramModel::PipelineDiagramModel(QObject *parent)
    : QAbstractTableModel(parent) {
  connect(ProcessorHandler::get(), &ProcessorHandler::processorClocked, this,
          &PipelineDiagramModel::processorWasClocked, Qt::DirectConnection);
  connect(ProcessorHandler::get(), &ProcessorHandler::processorReset, this,
          &PipelineDiagramModel::reset);
}

QVariant PipelineDiagramModel::headerData(int section,
                                          Qt::Orientation orientation,
                                          int role) const {
  if (role != Qt::DisplayRole)
    return QVariant();
  if (orientation == Qt::Horizontal) {
    // Cycle number
    // MODIFIED: number columns from one instead of zero
    return QString::number(section + 1);
  } else {
    const auto addr = indexToAddress(section);
    return ProcessorHandler::disassembleInstr(addr);
  }
}

int PipelineDiagramModel::rowCount(const QModelIndex &) const {
  return ProcessorHandler::getCurrentProgramSize() /
         ProcessorHandler::currentISA()->instrBytes();
}

int PipelineDiagramModel::columnCount(const QModelIndex &) const {
  return m_cycleStageInfos.size();
}

void PipelineDiagramModel::processorWasClocked() {
  const auto maxCycles =
      RipesSettings::value(RIPES_SETTING_PIPEDIAGRAM_MAXCYCLES).toInt();
  const auto cycleCount = ProcessorHandler::getProcessor()->getCycleCount();

  if (m_atMaxCycles) {
    return;
  }

  gatherStageInfo();

  if (cycleCount >= maxCycles) {
    m_atMaxCycles = true;
  }

  // Implement sliding window to prevent continuously growing memory usage
  // during long simulations. Only cleanup when significantly over the limit to
  // reduce performance impact.
  const size_t cleanupThreshold =
      static_cast<size_t>(maxCycles * 1.2); // 20% over limit
  if (m_cycleStageInfos.size() > cleanupThreshold) {
    // Notify model views before data changes
    beginResetModel();

    // Keep only the most recent maxCycles entries
    const size_t targetSize = static_cast<size_t>(maxCycles);
    auto it = m_cycleStageInfos.begin();
    std::advance(it, m_cycleStageInfos.size() - targetSize);
    const long long firstRetainedCycle = it->first;
    m_cycleStageInfos.erase(m_cycleStageInfos.begin(), it);
    m_cycleFPStageInfos.erase(
        m_cycleFPStageInfos.begin(),
        m_cycleFPStageInfos.lower_bound(firstRetainedCycle));

    // Notify model views after data changes
    endResetModel();
  }
}

void PipelineDiagramModel::reset() {
  m_atMaxCycles = false;
  m_cycleStageInfos.clear();
  m_cycleFPStageInfos.clear();
  gatherStageInfo();
}

void PipelineDiagramModel::prepareForView() {
  beginResetModel();
  endResetModel();
}

void PipelineDiagramModel::gatherStageInfo() {
  long long cycleCount = ProcessorHandler::getProcessor()->getCycleCount();
  auto stageInfoForCycle = m_cycleStageInfos.find(cycleCount);
  if (stageInfoForCycle != m_cycleStageInfos.end()) {
    // Already gathered stage info for this cycle.
    return;
  }
  m_cycleStageInfos[cycleCount];
  for (auto idx : ProcessorHandler::getProcessor()->structure().stageIt())
    m_cycleStageInfos[cycleCount][idx] =
        ProcessorHandler::getProcessor()->stageInfo(idx);
  m_cycleFPStageInfos[cycleCount] =
      ProcessorHandler::getProcessor()->fpUnicicleStageInfos();
}

QVariant PipelineDiagramModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid())
    return QVariant();

  if (role == Qt::TextAlignmentRole) {
    return Qt::AlignCenter;
  }

  if (role != Qt::DisplayRole)
    return QVariant();

  if (!m_cycleStageInfos.count(index.column()))
    return QVariant();

  const AInt addr = indexToAddress(index.row());
  const auto &stageInfo = m_cycleStageInfos.at(index.column());
  const auto fpStageIt = m_cycleFPStageInfos.find(index.column());

  QStringList stagesForAddr;
  QString stageStr;
  for (const auto &si : stageInfo) {
    if (si.second.pc == addr && si.second.stage_valid &&
        si.second.state == StageInfo::State::None) {
      const bool representedByFPStage =
          fpStageIt != m_cycleFPStageInfos.end() &&
          std::any_of(fpStageIt->second.begin(), fpStageIt->second.end(),
                      [addr](const FPUnicicleStageInfo &fpStage) {
                        return fpStage.valid && fpStage.pc == addr;
                      }) &&
          ProcessorHandler::getProcessor()->stageName(si.first) == "EX";
      if (representedByFPStage) {
        continue;
      }
      if (m_cycleStageInfos.count(index.column() - 1)) {
        const auto &prevCycleStageInfo =
            m_cycleStageInfos.at(index.column() - 1);
        if (prevCycleStageInfo.at(si.first).stage_valid &&
            prevCycleStageInfo.at(si.first).pc == si.second.pc) {
          stageStr =
              si.second.namedState.isEmpty() ? "-" : si.second.namedState;
          stagesForAddr << stageStr;
          continue;
        }
      }
      stageStr = si.second.namedState.isEmpty()
                     ? ProcessorHandler::getProcessor()->stageName(si.first)
                     : si.second.namedState;
      stagesForAddr << stageStr;
    }
  }

  if (fpStageIt != m_cycleFPStageInfos.end()) {
    for (const auto &fpStage : fpStageIt->second) {
      if (!fpStage.valid || fpStage.pc != addr) {
        continue;
      }
      const QString prefix =
          fpStage.unit == 1 ? "A" : fpStage.unit == 2 ? "M" : "D";
      stagesForAddr << prefix + QString::number(fpStage.stage + 1);
    }
  }

  if (stagesForAddr.size() == 0) {
    return QVariant();
  }

  return stagesForAddr.join('/');
}

QString PipelineDiagramModel::toString() const {
  QString textualRepr;

  // Copy headers
  textualRepr.append('\t');
  for (int j = 0; j < columnCount(); j++) {
    textualRepr.append(headerData(j, Qt::Horizontal).toString());
    textualRepr.append('\t');
  }
  textualRepr.append('\n');
  // Copy data
  for (int i = 0; i < rowCount(); ++i) {
    textualRepr.append(headerData(i, Qt::Vertical).toString());
    textualRepr.append('\t');
    for (int j = 0; j < columnCount(); j++) {
      textualRepr.append(data(index(i, j)).toString());
      textualRepr.append('\t');
    }
    textualRepr.append('\n');
  }
  return textualRepr;
}

} // namespace Ripes
